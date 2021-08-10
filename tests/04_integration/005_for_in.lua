function range(start, stop, step)
    if not step then
        step = 1
    end

    return function(initial, current)
        if current + step < stop then
            return current + step
        else
            return nil
        end
    end, start, start
end

for i in range(0, 10) do
    print (i)
end

print (i)
memory()
